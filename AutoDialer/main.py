from fastapi import FastAPI, HTTPException, Query
from pydantic import BaseModel
from sqlalchemy import create_engine, Column, Integer, String, DateTime, Boolean
from sqlalchemy.orm import sessionmaker, declarative_base
from twilio.rest import Client
from twilio.twiml.voice_response import Dial, VoiceResponse, Say
import os
import datetime


# --- Twilio Configuration ---
# Replace with your actual Twilio account credentials and Twilio phone number
ACCOUNT_SID = os.environ.get("TWILIO_ACCOUNT_SID")
AUTH_TOKEN = os.environ.get("TWILIO_AUTH_TOKEN")
PERSONAL_PHONE_NUMBER = os.environ.get("PERSONAL_PHONE_NUMBER")
twilio_client = Client(ACCOUNT_SID, AUTH_TOKEN)

# --- Database Setup ---
DATABASE_URL = "sqlite:///./calls.db"
engine = create_engine(DATABASE_URL, connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


class Call(Base):
    __tablename__ = "calls"
    id = Column(Integer, primary_key=True, index=True)
    phone_number = Column(String, unique=True, index=True)
    business_name = Column(String, nullable=True)
    google_maps_url = Column(String, nullable=True)
    created_at = Column(DateTime, default=datetime.datetime.utcnow)
    is_called = Column(Boolean, default=False, index=True)


# Create the database tables if they don't exist
Base.metadata.create_all(bind=engine)

# --- FastAPI Application ---
app = FastAPI()


# Request model for the POST endpoint
class CallRequest(BaseModel):
    phone_number: str
    business_name: str = ""
    google_maps_url: str = ""  # Request model for the POST endpoint


# Add call to queue
class AddPhoneNumberRequest(BaseModel):
    phone_number: str
    google_maps_url: str = ""


@app.post("/webhook")
def webhook():
    response = VoiceResponse()

    # Get the phone number from the database
    db = SessionLocal()
    try:
        # Check if the phone number is already recorded
        row = db.query(Call).filter(Call.is_called == False).first()
        row.is_called = True
        db.commit()
        response.dial(row.phone_number)
        return str(response)
    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=f"Failed to connect call: {str(e)}")
    finally:
        db.close()


@app.get("/call")
def check_call(phone_number: str = Query(..., description="Phone number to check")):
    db = SessionLocal()
    try:
        call_record = db.query(Call).filter(Call.phone_number == phone_number).first()
        if call_record:
            return {"called": True, "phone_number": phone_number}
        else:
            return {"called": False, "phone_number": phone_number}
    except Exception as e:
        raise HTTPException(
            status_code=500, detail=f"Failed to query call record: {str(e)}"
        )
    finally:
        db.close()


@app.post("/add_phone_number")
def add_phone_number(add_phone_number: AddPhoneNumberRequest):
    db = SessionLocal()
    try:
        # Check if the phone number is already recorded
        existing_call = (
            db.query(Call)
            .filter(Call.phone_number == add_phone_number.phone_number)
            .first()
        )
        if not existing_call:
            new_call = Call(
                phone_number=add_phone_number.phone_number,
                google_maps_url=add_phone_number.google_maps_url,
            )
            db.add(new_call)
            db.commit()
            db.refresh(new_call)
    except Exception as e:
        db.rollback()
        raise HTTPException(
            status_code=500, detail=f"Failed to save call record: {str(e)}"
        )
    finally:
        db.close()

    return {
        "message": "Phone number added",
        "phone_number": add_phone_number.phone_number,
    }
